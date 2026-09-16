#ifndef GV_TIMER_H
#define GV_TIMER_H

#include <stddef.h>
#include <stdint.h>

#define TIMER_NONE 0xFFFFFFFFu /* "no time recorded" */

/* Seconds -> centiseconds, rounded down, clamped to [0, TIMER_NONE-1]; negative/NaN -> 0. */
uint32_t timer_to_cs(float seconds);

/* Format centiseconds as "m:ss.cc" (e.g. 83456 -> "13:54.56"); TIMER_NONE -> "--:--.--".
 * Minutes are not capped. Always NUL-terminates when cap > 0. */
void timer_format(uint32_t cs, char *buf, size_t cap);

#endif
