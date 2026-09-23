#import "AppKitDropTarget.h"

#include "DragAndDrop.h"

#import <AppKit/AppKit.h>

#include <string>

namespace basalt {

facebook::react::Tag dropTargetAt(RnAppKitView *root, double x, double y, std::uint16_t accepts) {
  if (root == nil) {
    return 0;
  }
  // AppKit's own hit test, which already answers with the deepest view. From
  // there the walk up is ours, for the reason core/DragAndDrop.h gives: a
  // label inside a drop target should not need marking.
  NSView *hit = [root hitTest:NSMakePoint(x, y)];
  for (NSView *view = hit; view != nil; view = view.superview) {
    if ([view isKindOfClass:[RnAppKitView class]]) {
      auto *candidate = (RnAppKitView *)view;
      NSString *identifier = candidate.rnNativeId;
      if (identifier != nil) {
        const std::uint16_t wanted = dropAcceptsFrom(identifier.UTF8String);
        if (wanted != DropAcceptsNone && (wanted & accepts) != 0) {
          return (facebook::react::Tag)candidate.rnTag;
        }
      }
    }
    if (view == root) {
      break;
    }
  }
  return 0;
}

} // namespace basalt

// The dragging destination itself.
//
// On a view of its own, layered over the surface root, rather than on
// RnAppKitView: every RnAppKitView would otherwise inherit the protocol and
// AppKit would ask the deepest one, which is the opposite of the rule -- the
// deepest *marked* view decides, and most views are not marked.
@interface RnDropTargetView : NSView
@property(nonatomic, weak, nullable) RnAppKitView *rnRoot;
@property(nonatomic) facebook::react::Tag rnCurrent;
@end

@implementation RnDropTargetView

// Not drawn and not hit-testable for anything but drags: a view that swallowed
// clicks would break every press in the app.
- (NSView *)hitTest:(NSPoint)point {
  (void)point;
  return nil;
}

- (std::uint16_t)rnOfferedBy:(id<NSDraggingInfo>)info {
  std::uint16_t offered = basalt::DropAcceptsNone;
  NSPasteboard *board = info.draggingPasteboard;
  if ([board canReadObjectForClasses:@[ [NSURL class] ]
                             options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}]) {
    offered |= basalt::DropAcceptsFiles;
  }
  if ([board canReadObjectForClasses:@[ [NSString class] ] options:@{}]) {
    offered |= basalt::DropAcceptsText;
  }
  return offered;
}

// The point a drag is over, in the root's coordinates.
- (NSPoint)rnPointFor:(id<NSDraggingInfo>)info {
  RnAppKitView *root = self.rnRoot;
  if (root == nil) {
    return NSZeroPoint;
  }
  return [root convertPoint:info.draggingLocation fromView:nil];
}

- (void)rnReportLeaveAt:(NSPoint)point {
  if (self.rnCurrent == 0) {
    return;
  }
  basalt::DropEvent event;
  event.tag = self.rnCurrent;
  event.phase = basalt::DropPhase::Leave;
  event.x = point.x;
  event.y = point.y;
  basalt::reportDrop(event);
  self.rnCurrent = 0;
}

- (NSDragOperation)rnUpdate:(id<NSDraggingInfo>)info {
  const NSPoint point = [self rnPointFor:info];
  const facebook::react::Tag found =
      basalt::dropTargetAt(self.rnRoot, point.x, point.y, [self rnOfferedBy:info]);

  if (found != self.rnCurrent) {
    [self rnReportLeaveAt:point];
  }
  if (found == 0) {
    // Nothing here takes it, which is what makes the cursor say so.
    return NSDragOperationNone;
  }
  self.rnCurrent = found;

  basalt::DropEvent event;
  event.tag = found;
  event.phase = basalt::DropPhase::Over;
  event.x = point.x;
  event.y = point.y;
  basalt::reportDrop(event);
  return NSDragOperationCopy;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
  return [self rnUpdate:sender];
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
  return [self rnUpdate:sender];
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
  [self rnReportLeaveAt:sender != nil ? [self rnPointFor:sender] : NSZeroPoint];
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
  NSPasteboard *board = sender.draggingPasteboard;

  basalt::DragPayload payload;
  NSArray<NSURL *> *urls =
      [board readObjectsForClasses:@[ [NSURL class] ]
                           options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
  for (NSURL *url in urls) {
    if (url.isFileURL && url.path != nil) {
      // A path, not a URL: core/DragAndDrop.h says every host hands back
      // something path-shaped, and an app that wanted a URL can make one.
      payload.files.emplace_back(url.path.UTF8String);
    }
  }
  NSString *text = [board stringForType:NSPasteboardTypeString];
  if (text != nil) {
    payload.text = text.UTF8String;
  }

  // Asked again from the contents rather than from what the pasteboard could
  // read, for the reason the GTK host gives: this is the moment the payload is
  // known.
  std::uint16_t carries = basalt::DropAcceptsNone;
  if (payload.hasFiles()) {
    carries |= basalt::DropAcceptsFiles;
  }
  if (payload.hasText()) {
    carries |= basalt::DropAcceptsText;
  }

  const NSPoint point = [self rnPointFor:sender];
  const facebook::react::Tag found =
      basalt::dropTargetAt(self.rnRoot, point.x, point.y, carries);
  self.rnCurrent = 0;
  if (found == 0) {
    return NO;
  }

  basalt::DropEvent event;
  event.tag = found;
  event.phase = basalt::DropPhase::Drop;
  event.x = point.x;
  event.y = point.y;
  event.payload = payload;
  basalt::reportDrop(event);
  return YES;
}

@end

namespace basalt {

void attachDropTarget(RnAppKitView *root) {
  if (root == nil) {
    return;
  }
  RnDropTargetView *target = [[RnDropTargetView alloc] initWithFrame:root.bounds];
  target.rnRoot = root;
  target.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [target registerForDraggedTypes:@[ NSPasteboardTypeFileURL, NSPasteboardTypeString ]];
  // Above everything, so no app view can shadow the destination -- it is not
  // hit-testable for clicks, so being on top costs nothing.
  [root addSubview:target positioned:NSWindowAbove relativeTo:nil];
}

} // namespace basalt
