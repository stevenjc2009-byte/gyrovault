#include <string.h>

#include "test.h"
#include "version.h"

int main(void)
{
    char out[32];

    /* version_compare */
    CHECK(version_compare("v1.2.10", "1.2.9") > 0);
    CHECK(version_compare("1.2.9", "v1.2.10") < 0);
    CHECK(version_compare("1.2", "1.2.0") == 0);
    CHECK(version_compare("1", "1.0.0") == 0);
    CHECK(version_compare("v1.0.0", "1.0.0") == 0);
    CHECK(version_compare("1.0.1", "1.0.0") > 0);
    CHECK(version_compare("2.0.0", "1.99.99") > 0);
    CHECK(version_compare("1.10.0", "1.9.0") > 0);
    CHECK(version_compare("V2.0", "v1.9.9") > 0);
    CHECK(version_compare(GV_VERSION, GV_VERSION) == 0);
    /* garbage -> 0.0.0 */
    CHECK(version_compare("abc", "0.0.0") == 0);
    CHECK(version_compare("", "0.0.0") == 0);
    CHECK(version_compare(NULL, "0.0.0") == 0);
    CHECK(version_compare("v", "0") == 0);
    CHECK(version_compare("garbage", "0.0.1") < 0);
    CHECK(version_compare("1.0.1", "junk") > 0);
    CHECK(version_compare("1.2.3-beta", "1.2.3") == 0);
    CHECK(version_compare("99999999999999999999.0.0", "1.0.0") > 0);

    /* tag_from_location */
    CHECK(tag_from_location("https://github.com/o/r/releases/tag/v1.0.1", out, sizeof(out)) == 0);
    CHECK(strcmp(out, "v1.0.1") == 0);
    CHECK(tag_from_location("https://github.com/o/r/releases/tag/v1.2.10?foo=bar", out,
                            sizeof(out)) == 0);
    CHECK(strcmp(out, "v1.2.10") == 0);
    CHECK(tag_from_location("https://github.com/o/r/releases/tag/v2.0.0#frag", out,
                            sizeof(out)) == 0);
    CHECK(strcmp(out, "v2.0.0") == 0);
    CHECK(tag_from_location("https://github.com/o/r/releases/tag/1.3/", out, sizeof(out)) == 0);
    CHECK(strcmp(out, "1.3") == 0);
    CHECK(tag_from_location("https://github.com/o/r/releases/tag/v1.0.4\r\n", out,
                            sizeof(out)) == 0);
    CHECK(strcmp(out, "v1.0.4") == 0);
    /* failures */
    CHECK(tag_from_location("https://github.com/o/r/releases/latest", out, sizeof(out)) == -1);
    CHECK(tag_from_location("https://github.com/o/r/releases/tag/", out, sizeof(out)) == -1);
    CHECK(tag_from_location("https://github.com/o/r/releases/tag/?x", out, sizeof(out)) == -1);
    CHECK(tag_from_location("garbage", out, sizeof(out)) == -1);
    CHECK(tag_from_location("", out, sizeof(out)) == -1);
    CHECK(tag_from_location(NULL, out, sizeof(out)) == -1);
    CHECK(tag_from_location("https://x/releases/tag/v1.0.1", NULL, 8) == -1);
    /* cap: "v1.0.1" needs 7 bytes */
    CHECK(tag_from_location("https://x/releases/tag/v1.0.1", out, 6) == -1);
    CHECK(tag_from_location("https://x/releases/tag/v1.0.1", out, 0) == -1);
    CHECK(tag_from_location("https://x/releases/tag/v1.0.1", out, 7) == 0);
    CHECK(strcmp(out, "v1.0.1") == 0);

    return test_summary("version");
}
