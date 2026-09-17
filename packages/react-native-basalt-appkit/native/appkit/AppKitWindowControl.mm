// The macOS half of core/WindowControl.h.
//
// The one place this differs from the other two in more than spelling is the
// coordinate system. AppKit's origin is the bottom left of the *primary*
// display and y grows upwards; every other desktop, and React Native, put the
// origin at the top left and grow downwards. So `windowBounds` and
// `setWindowPosition` flip, against the primary screen's height, and an app
// that remembers a position and restores it gets the same window back.
//
// Not flipping would be the subtler bug: a window would be restored the same
// distance from the wrong edge, which looks almost right on a large display and
// entirely wrong on a small one.

#include "WindowControl.h"

#import <Cocoa/Cocoa.h>

namespace basalt {

namespace {


NSWindow *window() {
  return NSApp.keyWindow != nil ? NSApp.keyWindow : NSApp.mainWindow;
}

// The height AppKit measures y from. `NSScreen.screens.firstObject` is the
// primary display, which is the one AppKit's global coordinates are anchored
// to -- not `mainScreen`, which is wherever the key window happens to be.
CGFloat primaryHeight() {
  NSScreen *primary = NSScreen.screens.firstObject;
  return primary != nil ? NSMaxY(primary.frame) : 0.0;
}

} // namespace

WindowBounds windowBounds() {
  WindowBounds bounds;
  @autoreleasepool {
    NSWindow *target = window();
    if (target == nil) {
      return bounds;
    }
    const NSRect frame = target.frame;
    bounds.x = frame.origin.x;
    // Top-left origin, growing downwards. See the header.
    bounds.y = primaryHeight() - NSMaxY(frame);
    bounds.width = frame.size.width;
    bounds.height = frame.size.height;
    bounds.fullScreen = (target.styleMask & NSWindowStyleMaskFullScreen) != 0;
    bounds.maximized = target.isZoomed;
  }
  return bounds;
}

void setWindowSize(double width, double height) {
  if (width <= 0.0 || height <= 0.0) {
    return;
  }
  @autoreleasepool {
    NSWindow *target = window();
    if (target == nil) {
      return;
    }
    NSRect frame = target.frame;
    // The top edge stays where it is. Resizing a window from its bottom-left
    // origin -- which is what setting the size alone would do -- moves its
    // title bar, and every other desktop grows a window downwards.
    frame.origin.y = NSMaxY(frame) - height;
    frame.size = NSMakeSize(width, height);
    [target setFrame:frame display:YES animate:NO];
  }
}

void setWindowPosition(double x, double y) {
  @autoreleasepool {
    NSWindow *target = window();
    if (target == nil) {
      return;
    }
    // `setFrameTopLeftPoint` takes AppKit coordinates, so the y this was given
    // -- measured down from the top -- is measured back up from the bottom.
    [target setFrameTopLeftPoint:NSMakePoint(x, primaryHeight() - y)];
  }
}

void centerWindow() {
  @autoreleasepool {
    // AppKit's own, which puts the window slightly above centre: that is where
    // Apple's guidelines say a window goes, and an app asking to be centred on
    // a Mac is asking for that rather than for arithmetic.
    [window() center];
  }
}

void setWindowFullScreen(bool fullScreen) {
  @autoreleasepool {
    NSWindow *target = window();
    if (target == nil) {
      return;
    }
    const bool already = (target.styleMask & NSWindowStyleMaskFullScreen) != 0;
    // `toggleFullScreen:` is the only way in, and it toggles -- so asking for
    // the state it is already in has to do nothing rather than reverse it.
    if (already != fullScreen) {
      [target toggleFullScreen:nil];
    }
  }
}


} // namespace basalt
