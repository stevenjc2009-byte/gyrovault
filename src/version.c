#include "version.h"

#include <string.h>

#define VERSION_PARTS 3
#define PART_CAP 100000000UL

static int is_digit(char c)
{
    return c >= '0' && c <= '9';
}

/* "v1.2.10-beta" -> {1,2,10}. Missing parts are 0; anything unparseable is 0.0.0. */
static void parse_version(const char *s, unsigned long out[VERSION_PARTS])
{
    int i;
    for (i = 0; i < VERSION_PARTS; i++)
        out[i] = 0;
    if (!s)
        return;
    while (*s == ' ' || *s == '\t')
        s++;
    if (*s == 'v' || *s == 'V')
        s++;
    for (i = 0; i < VERSION_PARTS; i++) {
        unsigned long n = 0;
        if (!is_digit(*s))
            break;
        while (is_digit(*s)) {
            if (n < PART_CAP)
                n = n * 10 + (unsigned long)(*s - '0');
            s++;
        }
        out[i] = n;
        if (*s != '.')
            break;
        s++;
    }
}

int version_compare(const char *a, const char *b)
{
    unsigned long va[VERSION_PARTS], vb[VERSION_PARTS];
    int i;
    parse_version(a, va);
    parse_version(b, vb);
    for (i = 0; i < VERSION_PARTS; i++) {
        if (va[i] < vb[i])
            return -1;
        if (va[i] > vb[i])
            return 1;
    }
    return 0;
}

int tag_from_location(const char *location, char *out, size_t cap)
{
    static const char seg[] = "/releases/tag/";
    const char *start, *end;
    size_t len;

    if (!location || !out || cap == 0)
        return -1;
    start = strstr(location, seg);
    if (!start)
        return -1;
    start += sizeof(seg) - 1;
    end = start;
    while (*end && *end != '?' && *end != '#' && *end != '/' &&
           *end != '\r' && *end != '\n' && *end != ' ' && *end != '\t')
        end++;
    len = (size_t)(end - start);
    if (len == 0 || len + 1 > cap)
        return -1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 0;
}
