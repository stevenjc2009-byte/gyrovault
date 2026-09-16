#include "timer.h"

#include <float.h>
#include <stdio.h>
#include <string.h>

uint32_t timer_to_cs(float seconds)
{
    double raw, cs;

    if (!(seconds > 0.0f)) /* also catches NaN (comparison is false) and <= 0 */
        return 0;
    raw = (double)seconds * 100.0;
    /* A float32 decimal literal (e.g. 834.56f) is stored as the nearest representable
     * value, off by up to ~half a ULP (834.56f is actually 834.55999755...). Nudge by a
     * magnitude-scaled epsilon to absorb that noise so seconds -> cs round-trips as
     * expected; it stays orders of magnitude below any genuine rounding-down decision. */
    cs = raw + raw * (2.0 * (double)FLT_EPSILON) + 1e-7;
    if (cs >= (double)(TIMER_NONE - 1))
        return TIMER_NONE - 1;
    return (uint32_t)cs; /* positive, so the truncation rounds down */
}

void timer_format(uint32_t cs, char *buf, size_t cap)
{
    char tmp[32];
    unsigned m, s, c;
    size_t len;

    if (cap == 0)
        return;
    if (cs == TIMER_NONE) {
        snprintf(buf, cap, "--:--.--");
        return;
    }

    m = cs / 6000;
    s = (cs / 100) % 60;
    c = cs % 100;
    snprintf(tmp, sizeof tmp, "%u:%02u.%02u", m, s, c);

    len = strlen(tmp);
    if (len >= cap)
        len = cap - 1;
    memcpy(buf, tmp, len);
    buf[len] = '\0';
}
