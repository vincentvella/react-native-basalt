// Hit testing.
//
// The same questions tests/test_hittest.cpp asks of GTK and the hit-test half
// of tests/test_appkit_input.mm asks of AppKit, in the same order, plus two
// this platform has to answer for itself.
//
// Both other hosts inherit picking from their toolkit -- `gtk_widget_pick` and
// `-[NSView hitTest:]` -- so the work there is making the toolkit's geometry
// agree with Fabric's. There is no toolkit geometry here, so this is written
// out, which is more code and one fewer thing that can silently disagree: the
// transform a view is drawn with is the transform a press is tested against,
// because `localToParent` is the only place either of them is composed.

#include "TestHarness.h"

#include "RnWin32View.h"

#include <memory>
#include <sstream>
#include <vector>

using basalt::win32::hitTest;
using basalt::win32::RnWin32View;

namespace {

class Tree {
 public:
  RnWin32View *box(int32_t tag, float x, float y, float w, float h) {
    auto view = std::make_unique<RnWin32View>(tag);
    view->setFrame(x, y, w, h);
    RnWin32View *raw = view.get();
    views_.push_back(std::move(view));
    return raw;
  }

 private:
  std::vector<std::unique_ptr<RnWin32View>> views_;
};

// The tag of whatever is under a point, or -1 for a miss. Comparing tags rather
// than pointers makes a failure say which view it found.
int32_t tagAt(RnWin32View *root, float x, float y) {
  RnWin32View *hit = hitTest(root, x, y);
  return hit == nullptr ? -1 : hit->tag();
}

} // namespace

TEST(hit_test_finds_the_view_under_a_point) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  root->insertChild(tree.box(2, 50, 50, 100, 100), 0);

  EXPECT_EQ(tagAt(root, 100, 100), 2);
}

TEST(hit_test_uses_top_left_coordinates) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  // Near the top of the root. On a coordinate system with the origin at the
  // bottom -- AppKit's, unflipped -- this point would be over nothing.
  root->insertChild(tree.box(2, 0, 0, 400, 40), 0);

  EXPECT_EQ(tagAt(root, 200, 20), 2);
  EXPECT_EQ(tagAt(root, 200, 380), 1);
}

TEST(hit_test_misses_return_the_root_not_a_child) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  root->insertChild(tree.box(2, 50, 50, 100, 100), 0);

  EXPECT_EQ(tagAt(root, 300, 300), 1);
}

TEST(hit_test_outside_the_root_is_a_miss) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);

  EXPECT_EQ(tagAt(root, 500, 200), -1);
  EXPECT_EQ(tagAt(root, -1, 200), -1);
}

TEST(hit_test_returns_the_deepest_view) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *middle = tree.box(2, 50, 50, 200, 200);
  middle->insertChild(tree.box(3, 25, 25, 50, 50), 0);
  root->insertChild(middle, 0);

  // (100,100) is inside all three; the innermost wins.
  EXPECT_EQ(tagAt(root, 100, 100), 3);
}

TEST(hit_test_respects_sibling_order) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  root->insertChild(tree.box(2, 0, 0, 200, 200), 0);
  root->insertChild(tree.box(3, 0, 0, 200, 200), 1);

  // Exactly overlapping. The later child is painted on top, so it is the one a
  // press lands on.
  EXPECT_EQ(tagAt(root, 100, 100), 3);
}

TEST(hit_test_follows_z_index) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *under = tree.box(2, 0, 0, 200, 200);
  RnWin32View *over = tree.box(3, 0, 0, 200, 200);
  root->insertChild(under, 0);
  root->insertChild(over, 1);

  // A negative zIndex on the later child puts it underneath, and a press has to
  // follow the paint order rather than the child list -- otherwise a view can
  // be drawn behind another and still swallow its clicks.
  over->setZIndex(-1);
  EXPECT_EQ(tagAt(root, 100, 100), 2);
}

TEST(hit_test_follows_a_scroll_offset) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *scroller = tree.box(50, 0, 0, 200, 200);
  scroller->insertChild(tree.box(51, 0, 300, 200, 100), 0);
  root->insertChild(scroller, 0);

  // The row sits below the viewport, so nothing of it is under this point.
  EXPECT_EQ(tagAt(root, 100, 50), 50);

  scroller->setScrollOffset(0, 300);
  EXPECT_EQ(tagAt(root, 100, 50), 51);
}

TEST(hit_test_skips_hidden_views) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *hidden = tree.box(2, 0, 0, 200, 200);
  root->insertChild(hidden, 0);

  EXPECT_EQ(tagAt(root, 100, 100), 2);

  // `display: none` takes a view out of painting and out of picking together.
  // Distinct from opacity 0, which is still there to be pressed.
  hidden->setHidden(true);
  EXPECT_EQ(tagAt(root, 100, 100), 1);
}

TEST(hit_test_skips_the_children_of_a_hidden_view) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *hidden = tree.box(2, 0, 0, 200, 200);
  hidden->insertChild(tree.box(3, 0, 0, 100, 100), 0);
  root->insertChild(hidden, 0);

  hidden->setHidden(true);
  EXPECT_EQ(tagAt(root, 50, 50), 1);
}

TEST(hit_test_follows_a_transform) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  // The same wide, short card the paint tests rotate, at the same coordinates,
  // so the two suites corroborate rather than each asserting its own idea of
  // where the thing went.
  RnWin32View *card = tree.box(2, 10, 40, 80, 20);
  root->insertChild(card, 0);

  EXPECT_EQ(tagAt(root, 20, 50), 2);
  EXPECT_EQ(tagAt(root, 50, 20), 1);

  // A quarter turn about the centre: the card now covers x 40..60, y 10..90.
  const float quarterTurn[16] = {
      0, 1, 0, 0, //
      -1, 0, 0, 0, //
      0, 0, 1, 0, //
      0, 0, 0, 1};
  card->setTransform(quarterTurn);

  // This is the assertion the whole inverse-matrix path exists for. A platform
  // that draws the transform but does not pick through it leaves a rotated
  // button clickable where it used to be, and both of these still pass the
  // *first* way round -- which is why both are here.
  EXPECT_EQ(tagAt(root, 50, 20), 2);
  EXPECT_EQ(tagAt(root, 20, 50), 1);
}

TEST(hit_test_on_a_null_root_is_a_miss) {
  EXPECT_EQ(tagAt(nullptr, 10, 10), -1);
}
