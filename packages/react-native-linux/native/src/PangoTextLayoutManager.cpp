// The Linux implementation of React Native's text measurement seam.
//
// `TextLayoutManager` is declared in React Native's cxx platform variant, and
// its only implementation there is a stub that ignores every attribute and
// returns `layoutConstraints.minimumSize`. That is why text has never had a
// size on this platform. This file replaces that stub -- the header is
// unchanged and shared, only the .cpp differs, and cmake/ReactNativeCore.cmake
// drops React Native's from the build so these definitions are the only ones.
//
// Yoga calls this during layout, on whichever thread is committing, so it must
// be thread-safe and it must be fast. `textMeasureCache_` handles the second
// part: Yoga measures the same string repeatedly while resolving flex.

#include "PangoTextLayout.h"

#include <react/renderer/textlayoutmanager/TextLayoutManager.h>

namespace facebook::react {

TextLayoutManager::TextLayoutManager(const std::shared_ptr<const ContextContainer> &contextContainer)
    : contextContainer_(contextContainer), textMeasureCache_(kSimpleThreadSafeCacheSizeCap) {}

TextMeasurement TextLayoutManager::measure(const AttributedStringBox &attributedStringBox,
                                           const ParagraphAttributes &paragraphAttributes,
                                           const TextLayoutContext &layoutContext,
                                           const LayoutConstraints &layoutConstraints) const {
  const auto &attributedString = attributedStringBox.getValue();

  const TextMeasureCacheKey key{
      .attributedString = attributedString,
      .paragraphAttributes = paragraphAttributes,
      .layoutConstraints = layoutConstraints,
      .pointScaleFactor = layoutContext.pointScaleFactor,
  };

  return textMeasureCache_.get(key, [&]() {
    // An infinite maximum width means "do not wrap"; Pango wants -1 for that.
    const float maxWidth = std::isinf(layoutConstraints.maximumSize.width)
        ? -1.0F
        : static_cast<float>(layoutConstraints.maximumSize.width);

    // pointScaleFactor is part of the cache key above but not of the layout:
    // sizes here are logical, and GTK scales the rendered result. Keeping it in
    // the key only makes the cache finer-grained than it strictly needs to be.
    PangoLayout *layout = rnlinux::buildTextLayout(attributedString, paragraphAttributes, maxWidth);

    float width = 0;
    float height = 0;
    rnlinux::textLayoutSize(layout, &width, &height);
    g_object_unref(layout);

    // Pango can report a line slightly wider than the width it was given, for
    // an unbreakable run. Yoga treats the returned size as final, so clamping
    // here keeps the paragraph inside the box its parent allotted.
    const auto measured = Size{
        .width = std::min(static_cast<Float>(width), layoutConstraints.maximumSize.width),
        .height = static_cast<Float>(height),
    };

    // Inline views (`<Text><View/></Text>`) reach here as attachment fragments.
    // Reporting a zero frame for each keeps the count right, which is what
    // ParagraphShadowNode iterates over, but they are not positioned yet: doing
    // that properly needs PangoAttrShape placeholders sized from the child's
    // own measurement. See plan/backlog.md.
    TextMeasurement::Attachments attachments;
    for (const auto &fragment : attributedString.getFragments()) {
      if (fragment.isAttachment()) {
        attachments.push_back(TextMeasurement::Attachment{
            .frame = {.origin = {.x = 0, .y = 0}, .size = {.width = 0, .height = 0}},
            .isClipped = false,
        });
      }
    }

    return TextMeasurement{.size = layoutConstraints.clamp(measured), .attachments = attachments};
  });
}

} // namespace facebook::react
