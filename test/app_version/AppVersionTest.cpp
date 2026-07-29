#include <cstdio>

#include "lib/AppVersion/SemanticVersion.h"

static int testsPassed = 0;
static int testsFailed = 0;

#define EXPECT_COMPARE(latest, current, expected)                                           \
  do {                                                                                      \
    const int actual = DuetVersion::compare((latest), (current));                           \
    if (actual != (expected)) {                                                             \
      std::fprintf(stderr, "FAIL: compare(%s, %s) = %d, expected %d\n", (latest), (current), \
                   actual, (expected));                                                     \
      ++testsFailed;                                                                        \
    } else {                                                                                \
      ++testsPassed;                                                                        \
    }                                                                                       \
  } while (0)

int main() {
  EXPECT_COMPARE("v0.1.0-alpha.2", "0.1.0-alpha.1-tiny", 1);
  EXPECT_COMPARE("v0.1.0-alpha.1", "0.1.0-alpha.1-xlarge", 0);
  EXPECT_COMPARE("v0.1.0-beta.1", "0.1.0-alpha.9", 1);
  EXPECT_COMPARE("v0.1.0-rc.1", "0.1.0-beta.4", 1);
  EXPECT_COMPARE("v0.1.0", "0.1.0-rc.8", 1);
  EXPECT_COMPARE("v0.1.0-alpha.1", "0.1.0", -1);
  EXPECT_COMPARE("v0.2.0-alpha.1", "0.1.9", 1);
  EXPECT_COMPARE("v0.1.0-alpha.1", "0.1.1-alpha.1", -1);
  EXPECT_COMPARE("not-a-version", "0.1.0-alpha.1", 0);

  std::printf("SemanticVersion: %d passed, %d failed\n", testsPassed, testsFailed);
  return testsFailed == 0 ? 0 : 1;
}
