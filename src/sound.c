#include "sound.h"
#include "sound_synth.h"

#include <psp2/kernel/threadmgr.h>
#include <psp2/audioout.h>

/* All the maths lives in sound_synth.c; this file is only the sceAudioOut port, the
 * dedicated mixing thread, and the mutex that guards the shared SynthState between the
 * game (UI) thread and the audio thread. */

#define THREAD_STACK  (64 * 1024)
#define THREAD_PRIO   0x10000100
#define GRAIN_FRAMES  512 /* mono samples mixed per sceAudioOutOutput() call */

static SceKernelLwMutexWork s_lock;
static SynthState    s_synth;
static SceUID         s_thread = -1;
static int            s_port = -1;
static volatile int   s_running; /* 1 while the audio thread should keep mixing/outputting */
static int            s_inited;

static void lock(void)   { sceKernelLockLwMutex(&s_lock, 1, NULL); }
static void unlock(void) { sceKernelUnlockLwMutex(&s_lock, 1); }

static int audio_thread(SceSize args, void *argp)
{
    int16_t buf[GRAIN_FRAMES];

    (void)args;
    (void)argp;

    while (s_running) {
        lock();
        synth_render(&s_synth, buf, GRAIN_FRAMES);
        unlock();
        /* Blocks until the port has consumed the previous buffer; this paces the thread
         * to real time, so there is no sleep/spin here. */
        sceAudioOutOutput(s_port, buf);
    }
    return sceKernelExitThread(0);
}

int sound_init(void)
{
    if (s_inited)
        return 0;

    if (sceKernelCreateLwMutex(&s_lock, "gv_sound_lock", 0, 0, NULL) < 0)
        return -1;

    synth_init(&s_synth);

    s_port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM, GRAIN_FRAMES,
                                  SYNTH_SAMPLE_RATE, SCE_AUDIO_OUT_MODE_MONO);
    if (s_port < 0) {
        s_port = -1;
        sceKernelDeleteLwMutex(&s_lock);
        return -1;
    }

    s_running = 1;
    s_thread = sceKernelCreateThread("gv_sound", audio_thread, THREAD_PRIO, THREAD_STACK, 0, 0,
                                      NULL);
    if (s_thread < 0) {
        s_running = 0;
        sceAudioOutReleasePort(s_port);
        s_port = -1;
        s_thread = -1;
        sceKernelDeleteLwMutex(&s_lock);
        return -1;
    }

    if (sceKernelStartThread(s_thread, 0, NULL) < 0) {
        s_running = 0;
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
        sceAudioOutReleasePort(s_port);
        s_port = -1;
        sceKernelDeleteLwMutex(&s_lock);
        return -1;
    }

    s_inited = 1;
    return 0;
}

void sound_shutdown(void)
{
    if (!s_inited)
        return;

    /* audio_thread is parked inside sceAudioOutOutput(); dropping s_running makes it
     * return and exit the next time the port accepts a buffer (within one grain period,
     * ~10.7ms at 48kHz/512 frames), so the wait below is bounded. */
    s_running = 0;
    sceKernelWaitThreadEnd(s_thread, NULL, NULL);
    sceKernelDeleteThread(s_thread);
    s_thread = -1;

    sceAudioOutReleasePort(s_port);
    s_port = -1;

    sceKernelDeleteLwMutex(&s_lock);

    s_inited = 0;
}

void sound_set_rolling(float speed)
{
    if (!s_inited)
        return;
    lock();
    synth_set_rolling(&s_synth, speed);
    unlock();
}

void sound_play(SoundEvent ev, float intensity)
{
    if (!s_inited)
        return;
    lock();
    synth_trigger(&s_synth, ev, intensity);
    unlock();
}
