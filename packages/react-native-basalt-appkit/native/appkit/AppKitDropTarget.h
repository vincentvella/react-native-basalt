// The AppKit half of drag and drop: what happens when something is dragged
// onto the window.
//
// See core/DragAndDrop.h for the model, which is shared, and for why a view
// declares itself a drop target by its nativeID.

#pragma once

#import "RnAppKitView.h"

#include <cstdint>

#include <react/renderer/core/ReactPrimitives.h>

namespace basalt {

// The deepest view under this point that would take what is being dragged, or
// 0 for none. The point is in the root view's coordinates.
facebook::react::Tag dropTargetAt(RnAppKitView *root, double x, double y, std::uint16_t accepts);

// Makes the surface root accept files and text dragged onto it.
void attachDropTarget(RnAppKitView *root);

} // namespace basalt
