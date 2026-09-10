#include "TestHarness.h"

#include <gtk/gtk.h>

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

int main(int argc, char **argv) {
  (void)argc;
  (void)argv;

  // Every test touches widgets, and a GtkWidget cannot be constructed before
  // GTK is initialised. No window is ever presented, but GTK still needs a
  // display connection: on a headless machine run this under a nested or
  // virtual display server. See docs/TESTING.md.
  if (gtk_init_check() == FALSE) {
    std::cerr << "could not initialise GTK; these tests need a display.\n"
              << "On a headless Linux box: xvfb-run -a ./build/rn_tests\n";
    return 77; // The convention automake uses for "skipped", not "failed".
  }

  std::cout << "running " << rnlinux::testing::registry().size() << " tests\n";
  return rnlinux::testing::runAllTests();
}
