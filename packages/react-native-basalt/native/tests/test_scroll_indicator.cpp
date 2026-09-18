// Where a scrollbar's thumb goes. Arithmetic, so it can be asserted without a
// display -- which matters more here than usual, because a scrollbar is pure
// paint and paint is what these hosts cannot otherwise check.

#include "TestHarness.h"

#include "ScrollIndicator.h"

#include <sstream>

using basalt::kScrollIndicatorInset;
using basalt::kScrollIndicatorMinimumLength;
using basalt::ScrollIndicator;
using basalt::scrollIndicatorFor;

namespace {

TEST(indicator_content_that_fits_has_none) {
  // A scrollbar for content that cannot scroll is noise, and every list shorter
  // than its container would otherwise grow one.
  EXPECT(!scrollIndicatorFor(400, 400, 0).visible);
  EXPECT(!scrollIndicatorFor(400, 120, 0).visible);
}

TEST(indicator_a_long_list_gets_a_short_thumb) {
  // The thumb is the visible fraction: a quarter on screen, a quarter of the
  // track.
  const ScrollIndicator indicator = scrollIndicatorFor(400, 1600, 0);
  EXPECT(indicator.visible);
  const double track = 400 - 2 * kScrollIndicatorInset;
  EXPECT(indicator.length > track * 0.24);
  EXPECT(indicator.length < track * 0.26);
}

TEST(indicator_a_very_long_list_still_has_a_grabbable_thumb) {
  // Without a floor this is under a pixel, which stops reading as a position.
  const ScrollIndicator indicator = scrollIndicatorFor(400, 400000, 0);
  EXPECT_EQ(indicator.length, kScrollIndicatorMinimumLength);
}

TEST(indicator_at_the_top_it_sits_at_the_top) {
  const ScrollIndicator indicator = scrollIndicatorFor(400, 1600, 0);
  EXPECT_EQ(indicator.offset, kScrollIndicatorInset);
}

TEST(indicator_at_the_bottom_it_sits_at_the_bottom) {
  // The end of the track, not past it: the thumb's far edge lands on the inset.
  const ScrollIndicator indicator = scrollIndicatorFor(400, 1600, 1200);
  const double track = 400 - 2 * kScrollIndicatorInset;
  EXPECT_EQ(indicator.offset + indicator.length, kScrollIndicatorInset + track);
}

TEST(indicator_halfway_is_halfway) {
  const ScrollIndicator indicator = scrollIndicatorFor(400, 1600, 600);
  const double track = 400 - 2 * kScrollIndicatorInset;
  const double expected = kScrollIndicatorInset + 0.5 * (track - indicator.length);
  EXPECT_EQ(indicator.offset, expected);
}

TEST(indicator_an_overscroll_does_not_move_it_past_the_ends) {
  // An elastic overscroll goes negative at the top and past the end at the
  // bottom. A thumb that vanished there would flicker at exactly the moment
  // somebody is looking at it.
  const ScrollIndicator above = scrollIndicatorFor(400, 1600, -80);
  EXPECT(above.visible);
  EXPECT_EQ(above.offset, kScrollIndicatorInset);

  const ScrollIndicator below = scrollIndicatorFor(400, 1600, 1600);
  const double track = 400 - 2 * kScrollIndicatorInset;
  EXPECT_EQ(below.offset + below.length, kScrollIndicatorInset + track);
}

TEST(indicator_a_container_of_nothing_has_none) {
  // A view that has not been laid out yet, which happens on the first mount.
  EXPECT(!scrollIndicatorFor(0, 1600, 0).visible);
}

} // namespace
