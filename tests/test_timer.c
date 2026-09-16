#include <string.h>

#include "test.h"
#include "timer.h"

int main(void)
{
    char buf[64];

    /* --- timer_to_cs --- */
    CHECK(timer_to_cs(0.0f) == 0);
    CHECK(timer_to_cs(60.99f) == 6099);          /* 6099, per timer_format spec below */
    CHECK(timer_to_cs(834.56f) == 83456);         /* the timer_format docstring example */

    /* Rounds down: 1.239 s truncates to 123 cs, not 124. */
    CHECK(timer_to_cs(1.239f) == 123);

    /* Negative and NaN input clamp to 0. */
    CHECK(timer_to_cs(-5.0f) == 0);
    {
        float nan_val = 0.0f / 0.0f;
        CHECK(timer_to_cs(nan_val) == 0);
    }

    /* Huge input clamps to TIMER_NONE - 1, never collides with the sentinel. */
    CHECK(timer_to_cs(1.0e12f) == TIMER_NONE - 1);

    /* --- timer_format --- */
    timer_format(0, buf, sizeof buf);
    CHECK(strcmp(buf, "0:00.00") == 0);

    timer_format(6099, buf, sizeof buf);
    CHECK(strcmp(buf, "1:00.99") == 0);

    timer_format(83456, buf, sizeof buf);
    CHECK(strcmp(buf, "13:54.56") == 0);

    timer_format(TIMER_NONE, buf, sizeof buf);
    CHECK(strcmp(buf, "--:--.--") == 0);

    /* Minutes are not capped. */
    timer_format(600000, buf, sizeof buf); /* 6000 s = 100 min */
    CHECK(strcmp(buf, "100:00.00") == 0);

    /* A small cap truncates safely: always NUL-terminated, never overruns. */
    {
        char small[4];
        memset(small, 'X', sizeof small);
        timer_format(6099, small, sizeof small); /* full string is "1:00.99" (7 chars) */
        CHECK(strlen(small) < sizeof small);
        CHECK(small[sizeof small - 1] == '\0');
        CHECK(strncmp(small, "1:00.99", strlen(small)) == 0); /* whatever fit is correct */
    }

    /* cap == 1: only room for the NUL terminator. */
    {
        char tiny[1];
        tiny[0] = 'X';
        timer_format(6099, tiny, sizeof tiny);
        CHECK(tiny[0] == '\0');
    }

    return test_summary("timer");
}
