// What actually reached the pixels.
//
// Every other suite in this project asserts on the view tree: that a view has a
// colour, a frame and a transform. `docs/BACKLOG.md` has carried the gap that
// leaves since GTK -- "GTK's cairo renderer mangled every transform in the demo
// and no test noticed" -- because closing it on the other two platforms means a
// display server or an offscreen window and a display cycle.
//
// Direct2D renders into a WIC bitmap with no window and no display, so here it
// costs a function call. These are the assertions that would have caught that
// bug: a transform composed in the wrong order, a clip that does not clip, an
// opacity applied per-brush instead of per-subtree, and a zIndex that reorders
// nothing all leave the tree dump identical and the picture wrong.

#include "TestHarness.h"

#include "RnWin32View.h"
#include "Win32Snapshot.h"

#include <memory>
#include <sstream>
#include <vector>

using basalt::win32::RnPixel;
using basalt::win32::RnPixels;
using basalt::win32::RnWin32View;

namespace {

// Rounding through premultiplied storage costs a unit or two, and Direct2D
// antialiases edges -- so every point these tests ask about is well inside or
// well outside a shape, never on its boundary.
constexpr double kTolerance = 3.0;

class Tree {
 public:
  RnWin32View *box(int32_t tag, float x, float y, float w, float h) {
    auto view = std::make_unique<RnWin32View>(tag);
    view->setFrame(x, y, w, h);
    RnWin32View *raw = view.get();
    views_.push_back(std::move(view));
    return raw;
  }

  RnWin32View *
  colouredBox(int32_t tag, float x, float y, float w, float h, float r, float g, float b) {
    RnWin32View *view = box(tag, x, y, w, h);
    view->setBackgroundColor(r, g, b, 1.0f, true);
    return view;
  }

 private:
  std::vector<std::unique_ptr<RnWin32View>> views_;
};

} // namespace

// A macro rather than a function so a failure reports the line that asked,
// which is the only thing that makes a wrong pixel findable.
#define EXPECT_PIXEL(pixels, x, y, r, g, b, a)                                                     \
  do {                                                                                             \
    const RnPixel pixel = (pixels).at((x), (y));                                                   \
    EXPECT_NEAR(pixel.red, (r), kTolerance);                                                       \
    EXPECT_NEAR(pixel.green, (g), kTolerance);                                                     \
    EXPECT_NEAR(pixel.blue, (b), kTolerance);                                                      \
    EXPECT_NEAR(pixel.alpha, (a), kTolerance);                                                     \
  } while (false)

#define EXPECT_TRANSPARENT(pixels, x, y) EXPECT_NEAR((pixels).at((x), (y)).alpha, 0, kTolerance)

TEST(win32_paint_puts_a_child_at_its_frame) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  root->insertChild(tree.colouredBox(2, 20, 30, 40, 10, 1.0f, 0.0f, 0.0f), 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());

  EXPECT_PIXEL(pixels, 25, 35, 255, 0, 0, 255);
  EXPECT_TRANSPARENT(pixels, 5, 5);
  EXPECT_TRANSPARENT(pixels, 65, 35);
  EXPECT_TRANSPARENT(pixels, 25, 45);
}

TEST(win32_paint_composites_opacity_over_the_whole_subtree) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 60, 60);
  RnWin32View *faded = tree.colouredBox(2, 0, 0, 60, 60, 1.0f, 0.0f, 0.0f);
  faded->setOpacity(0.4f);
  root->insertChild(faded, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());

  // Still fully red, at 40% coverage. An opacity folded into the brush's alpha
  // would look the same here and wrong the moment two children overlap, which
  // is what the layer in RnWin32View::paint is for.
  EXPECT_PIXEL(pixels, 30, 30, 255, 0, 0, 102);
}

TEST(win32_paint_clips_children_only_when_overflow_is_hidden) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *parent = tree.box(2, 0, 0, 50, 50);
  parent->insertChild(tree.colouredBox(3, 0, 0, 100, 100, 0.0f, 1.0f, 0.0f), 0);
  root->insertChild(parent, 0);

  const RnPixels visible = basalt::win32::renderToPixels(*root);
  EXPECT(!visible.empty());
  EXPECT_PIXEL(visible, 25, 25, 0, 255, 0, 255);
  // overflow: visible is the default, so the child escapes its parent's box.
  EXPECT_PIXEL(visible, 75, 25, 0, 255, 0, 255);

  parent->setClipsChildren(true);
  const RnPixels clipped = basalt::win32::renderToPixels(*root);
  EXPECT(!clipped.empty());
  EXPECT_PIXEL(clipped, 25, 25, 0, 255, 0, 255);
  EXPECT_TRANSPARENT(clipped, 75, 25);
}

TEST(win32_paint_rotates_about_the_centre_and_in_the_right_direction) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  // Deliberately wide and short, so a quarter turn changes which pixels are
  // covered rather than only which way up they are.
  RnWin32View *card = tree.colouredBox(2, 10, 40, 80, 20, 0.0f, 0.0f, 1.0f);
  // A marker in the card's top-left corner. Without it a quarter turn and a
  // three-quarter turn are indistinguishable, because the card is symmetric
  // under a half turn -- and the difference between them is exactly the
  // transpose that no test on an axis-aligned box can see.
  card->insertChild(tree.colouredBox(3, 0, 0, 10, 10, 1.0f, 1.0f, 1.0f), 0);
  root->insertChild(card, 0);

  const RnPixels upright = basalt::win32::renderToPixels(*root);
  EXPECT(!upright.empty());
  // The card spans x 10..90, y 40..60; the marker is its corner, x 10..20,
  // y 40..50.
  EXPECT_PIXEL(upright, 30, 50, 0, 0, 255, 255);
  EXPECT_PIXEL(upright, 15, 45, 255, 255, 255, 255);
  EXPECT_TRANSPARENT(upright, 50, 20);

  // CSS rotate(90deg), in matrix3d's column-major order: m11=0, m12=1, m21=-1,
  // m22=0. On screen, with y downwards, that turns clockwise.
  const float quarterTurn[16] = {
      0, 1, 0, 0, //
      -1, 0, 0, 0, //
      0, 0, 1, 0, //
      0, 0, 0, 1};
  card->setTransform(quarterTurn);

  const RnPixels turned = basalt::win32::renderToPixels(*root);
  EXPECT(!turned.empty());

  // The card's centre is (50,50) and it is now 20 wide and 80 tall about it, so
  // the pixels it covers have swapped: (50,60) is inside and (20,50) is not.
  EXPECT_PIXEL(turned, 50, 60, 0, 0, 255, 255);
  EXPECT_TRANSPARENT(turned, 20, 50);

  // And the marker has swung to x 50..60, y 10..20 -- clockwise. The transposed
  // matrix, which is the mistake no axis-aligned test can see, would put it at
  // x 40..50, y 80..90 instead. Both ends are asserted, because the card covers
  // both and only the marker's colour tells them apart.
  EXPECT_PIXEL(turned, 55, 15, 255, 255, 255, 255);
  EXPECT_PIXEL(turned, 45, 85, 0, 0, 255, 255);
}

TEST(win32_paint_orders_children_by_z_index) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *under = tree.colouredBox(2, 0, 0, 50, 50, 1.0f, 0.0f, 0.0f);
  RnWin32View *over = tree.colouredBox(3, 0, 0, 50, 50, 0.0f, 1.0f, 0.0f);
  root->insertChild(under, 0);
  root->insertChild(over, 1);

  // Document order: the later child wins.
  const RnPixels ordered = basalt::win32::renderToPixels(*root);
  EXPECT(!ordered.empty());
  EXPECT_PIXEL(ordered, 25, 25, 0, 255, 0, 255);

  // A negative zIndex on the later child puts it underneath, and the child list
  // does not move -- which test_win32_view.cpp asserts separately, because
  // Fabric indexes into that list.
  over->setZIndex(-1);
  const RnPixels restacked = basalt::win32::renderToPixels(*root);
  EXPECT(!restacked.empty());
  EXPECT_PIXEL(restacked, 25, 25, 255, 0, 0, 255);
}

TEST(win32_paint_moves_children_by_the_scroll_offset) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *row = tree.colouredBox(2, 0, 50, 100, 20, 1.0f, 0.0f, 0.0f);
  root->insertChild(row, 0);

  const RnPixels unscrolled = basalt::win32::renderToPixels(*root);
  EXPECT(!unscrolled.empty());
  EXPECT_PIXEL(unscrolled, 50, 55, 255, 0, 0, 255);
  EXPECT_TRANSPARENT(unscrolled, 50, 25);

  // Scrolled down by 30, the row that was at y=50 is drawn at y=20 -- and its
  // frame has not changed, which is the invariant: Yoga decides where things
  // are and the offset is applied at paint time.
  root->setScrollOffset(0, 30);
  const RnPixels scrolled = basalt::win32::renderToPixels(*root);
  EXPECT(!scrolled.empty());
  EXPECT_PIXEL(scrolled, 50, 25, 255, 0, 0, 255);
  EXPECT_TRANSPARENT(scrolled, 50, 55);
  EXPECT_NEAR(row->frame().y, 50.0, 0.001);
}

TEST(win32_paint_rounds_the_background_to_the_corner_radius) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *rounded = tree.colouredBox(2, 0, 0, 100, 100, 1.0f, 0.0f, 0.0f);
  rounded->setCornerRadius(40.0f);
  root->insertChild(rounded, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());

  // The middle is filled and the corner is not, which is the whole claim. The
  // background is clipped to the radius whether or not children are.
  EXPECT_PIXEL(pixels, 50, 50, 255, 0, 0, 255);
  EXPECT_TRANSPARENT(pixels, 2, 2);
}
