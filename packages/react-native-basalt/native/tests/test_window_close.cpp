// Being asked before a window closes: the bookkeeping under it.
//
// The interesting parts of this feature are in three hosts and can only be
// answered end to end, but what decides whether a close is refused is a set of
// surface ids and a listener, and those are portable and cheap to be sure of.
//
// Worth asserting rather than assuming, because both ways it can be wrong are
// silent. A window that stays in the set after it closes marks whichever window
// is given that id next -- a window nobody asked to guard that refuses to
// close, which looks like a stuck window manager. And a listener left holding a
// module's emitter after the module has gone is a use-after-free reached from a
// close button.

#include "TestHarness.h"

#include "WindowHost.h"

#include <sstream>
#include <vector>

using facebook::react::SurfaceId;

namespace {

TEST(close_a_window_is_not_intercepted_by_default) {
  // The default matters more than it looks: every window that never asks for
  // this has to close exactly as it did before, and a close path that waits on
  // JavaScript to say "go ahead" would be a window that does not close while
  // the runtime is busy.
  EXPECT(!basalt::hostWindowCloseIntercepted(4242));
}

TEST(close_interception_is_per_window) {
  basalt::setHostWindowCloseIntercepted(11, true);
  EXPECT(basalt::hostWindowCloseIntercepted(11));
  EXPECT(!basalt::hostWindowCloseIntercepted(12));
  basalt::setHostWindowCloseIntercepted(11, false);
  EXPECT(!basalt::hostWindowCloseIntercepted(11));
}

TEST(close_a_window_that_closed_stops_being_intercepted) {
  // Surface ids are not reused today. Relying on that would be relying on it:
  // the cost of forgetting is nothing and the cost of remembering is a window
  // that refuses to close for a reason nobody can find.
  basalt::setHostWindowCloseIntercepted(21, true);
  basalt::setHostWindowClosedListener(nullptr);
  basalt::hostWindowClosed(21);
  EXPECT(!basalt::hostWindowCloseIntercepted(21));
}

TEST(close_a_request_reaches_the_listener_with_the_window) {
  std::vector<SurfaceId> asked;
  basalt::setHostWindowCloseRequestListener([&asked](SurfaceId id) { asked.push_back(id); });

  basalt::hostWindowCloseRequested(31);
  basalt::hostWindowCloseRequested(32);

  EXPECT_EQ((long)asked.size(), 2L);
  EXPECT_EQ((long)asked[0], 31L);
  EXPECT_EQ((long)asked[1], 32L);

  // Cleared, the way the module clears it on the way out. A listener that
  // outlives the module holding its emitter is a use-after-free with a close
  // button in front of it.
  basalt::setHostWindowCloseRequestListener(nullptr);
  basalt::hostWindowCloseRequested(33);
  EXPECT_EQ((long)asked.size(), 2L);
}

TEST(close_a_request_with_nobody_listening_is_not_a_crash) {
  // The order the host and the module start in is not something a close button
  // waits for.
  basalt::setHostWindowCloseRequestListener(nullptr);
  basalt::hostWindowCloseRequested(41);
}

} // namespace
