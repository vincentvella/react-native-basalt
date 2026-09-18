#include "ScrollIndicator.h"

#include <algorithm>

namespace basalt {

ScrollIndicator scrollIndicatorFor(double containerLength,
                                   double contentLength,
                                   double scrollOffset) {
  ScrollIndicator indicator;
  if (containerLength <= 0.0 || contentLength <= containerLength) {
    return indicator;
  }

  const double track = std::max(0.0, containerLength - 2.0 * kScrollIndicatorInset);
  if (track <= 0.0) {
    return indicator;
  }

  // The thumb is as long as the visible fraction of the content, with a floor.
  // A very long list would otherwise end up with a one-pixel thumb.
  const double fraction = containerLength / contentLength;
  const double length = std::min(track, std::max(kScrollIndicatorMinimumLength, track * fraction));

  const double furthest = contentLength - containerLength;
  const double progress = furthest > 0.0 ? std::clamp(scrollOffset / furthest, 0.0, 1.0) : 0.0;

  indicator.visible = true;
  indicator.length = length;
  indicator.offset = kScrollIndicatorInset + progress * (track - length);
  return indicator;
}

} // namespace basalt
