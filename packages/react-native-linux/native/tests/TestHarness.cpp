// The runner itself, with no toolkit in it.
//
// The toolkit lives in the `main` beside this file -- GTK has to be initialised
// before a widget can be constructed, and AppKit has its own requirements -- so
// each platform brings its own entry point and shares everything below.

#include "TestHarness.h"

#include <sstream>

namespace rnlinux::testing {

namespace {

struct CurrentTest {
  std::string name;
  std::vector<std::string> failures;
};

CurrentTest &current() {
  static CurrentTest test;
  return test;
}

} // namespace

std::vector<TestCase> &registry() {
  static std::vector<TestCase> cases;
  return cases;
}

void recordFailure(const std::string &where, const std::string &what) {
  current().failures.push_back(where + ": " + what);
}

int runAllTests() {
  int failed = 0;

  std::cout << "running " << registry().size() << " tests\n";

  for (const auto &test : registry()) {
    current().name = test.name;
    current().failures.clear();

    test.body();

    if (current().failures.empty()) {
      std::cout << "  ok    " << test.name << "\n";
    } else {
      ++failed;
      std::cout << "  FAIL  " << test.name << "\n";
      for (const auto &failure : current().failures) {
        std::cout << "        " << failure << "\n";
      }
    }
  }

  std::cout << "\n" << registry().size() - static_cast<size_t>(failed) << "/" << registry().size()
            << " passed\n";
  return failed == 0 ? 0 : 1;
}

} // namespace rnlinux::testing
