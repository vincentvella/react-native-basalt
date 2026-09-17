// The JavaScript boundary for a window's geometry, written once.
//
// Each host's `BasaltWindow` module registers these beside its own title-bar
// methods. They are free functions with the signature a TurboModule method has,
// so a host installs one with a line:
//
//     methodMap_["setSize"] = MethodMetadata{2, basalt::windowSetSize};
//
// Everything they do is in core/WindowControl.h, which is the part that differs
// per platform. Nothing here is per-platform at all, which is why it is not
// written three times -- the reading of arguments and the shape of the answer
// are exactly the things three copies would drift on.

#pragma once

#include <ReactCommon/TurboModule.h>

namespace basalt {

// `setSize(width, height)`. Ignores a call with anything but two numbers, which
// is what a JavaScript caller passing undefined looks like.
facebook::jsi::Value windowSetSize(facebook::jsi::Runtime &runtime,
                                   facebook::react::TurboModule &module,
                                   const facebook::jsi::Value *args,
                                   size_t count);

// `setPosition(x, y)`, in the desktop's coordinates.
facebook::jsi::Value windowSetPosition(facebook::jsi::Runtime &runtime,
                                       facebook::react::TurboModule &module,
                                       const facebook::jsi::Value *args,
                                       size_t count);

// `center()`, on whichever display the window is already on.
facebook::jsi::Value windowCenter(facebook::jsi::Runtime &runtime,
                                  facebook::react::TurboModule &module,
                                  const facebook::jsi::Value *args,
                                  size_t count);

// `setFullScreen(boolean)`.
facebook::jsi::Value windowSetFullScreen(facebook::jsi::Runtime &runtime,
                                         facebook::react::TurboModule &module,
                                         const facebook::jsi::Value *args,
                                         size_t count);

// `getBounds()` -> `{x, y, width, height, fullScreen, maximized}`.
facebook::jsi::Value windowGetBounds(facebook::jsi::Runtime &runtime,
                                     facebook::react::TurboModule &module,
                                     const facebook::jsi::Value *args,
                                     size_t count);

// The device event `useWindow()` listens for. Named here so the two sides
// cannot drift.
inline constexpr const char *kWindowBoundsEvent = "basaltWindowBoundsChanged";

} // namespace basalt
