#include <psp2/kernel/processmgr.h>

#include "game.h"
#include "motion.h"
#include "render.h"
#include "sound.h"

#define MAX_DT 0.1f

int main(void)
{
    if (render_init() != 0) {
        sceKernelExitProcess(0);
        return 0;
    }
    motion_init();
    sound_init(); /* failure is non-fatal: sound.h calls become silent no-ops */
    game_init();

    SceUInt64 prev = sceKernelGetProcessTimeWide();
    for (;;) {
        SceUInt64 now = sceKernelGetProcessTimeWide();
        float dt = (float)(now - prev) / 1000000.0f;
        prev = now;
        if (dt < 0.0f) dt = 0.0f;
        if (dt > MAX_DT) dt = MAX_DT;

        if (game_frame(dt) != 0)
            break;
    }

    game_shutdown();
    sound_shutdown();
    render_shutdown();
    sceKernelExitProcess(0);
    return 0;
}
